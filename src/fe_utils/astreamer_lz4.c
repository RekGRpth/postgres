/*-------------------------------------------------------------------------
 *
 * astreamer_lz4.c
 *
 * Archive streamers that deal with data compressed using lz4.
 * astreamer_lz4_compressor applies lz4 compression to the input stream,
 * and astreamer_lz4_decompressor does the reverse.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  src/fe_utils/astreamer_lz4.c
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"

#include <unistd.h>

#ifdef USE_LZ4
#include <lz4frame.h>
#endif

#include "common/logging.h"
#include "fe_utils/astreamer.h"

#ifdef USE_LZ4
typedef struct astreamer_lz4_frame
{
	astreamer	base;

	LZ4F_compressionContext_t cctx;
	LZ4F_decompressionContext_t dctx;
	LZ4F_preferences_t prefs;

	size_t		bytes_written;
	bool		header_written;
} astreamer_lz4_frame;

static void astreamer_lz4_compressor_content(astreamer *streamer,
											 astreamer_member *member,
											 const char *data, int len,
											 astreamer_archive_context context);
static void astreamer_lz4_compressor_finalize(astreamer *streamer);
static void astreamer_lz4_compressor_free(astreamer *streamer);

static const astreamer_ops astreamer_lz4_compressor_ops = {
	.content = astreamer_lz4_compressor_content,
	.finalize = astreamer_lz4_compressor_finalize,
	.free = astreamer_lz4_compressor_free
};

static void astreamer_lz4_decompressor_content(astreamer *streamer,
											   astreamer_member *member,
											   const char *data, int len,
											   astreamer_archive_context context);
static void astreamer_lz4_decompressor_finalize(astreamer *streamer);
static void astreamer_lz4_decompressor_free(astreamer *streamer);

static const astreamer_ops astreamer_lz4_decompressor_ops = {
	.content = astreamer_lz4_decompressor_content,
	.finalize = astreamer_lz4_decompressor_finalize,
	.free = astreamer_lz4_decompressor_free
};
#endif

/*
 * Create a new base backup streamer that performs lz4 compression of tar
 * blocks.
 */
astreamer *
astreamer_lz4_compressor_new(astreamer *next, pg_compress_specification *compress)
{
#ifdef USE_LZ4
	astreamer_lz4_frame *streamer;
	LZ4F_errorCode_t ctxError;
	LZ4F_preferences_t *prefs;

	Assert(next != NULL);

	streamer = palloc0(sizeof(astreamer_lz4_frame));
	*((const astreamer_ops **) &streamer->base.bbs_ops) =
		&astreamer_lz4_compressor_ops;

	streamer->base.bbs_next = next;
	initStringInfo(&streamer->base.bbs_buffer);
	streamer->header_written = false;

	/* Initialize stream compression preferences */
	prefs = &streamer->prefs;
	memset(prefs, 0, sizeof(LZ4F_preferences_t));
	prefs->frameInfo.blockSizeID = LZ4F_max256KB;
	prefs->compressionLevel = compress->level;

	ctxError = LZ4F_createCompressionContext(&streamer->cctx, LZ4F_VERSION);
	if (LZ4F_isError(ctxError))
		pg_log_error("could not create lz4 compression context: %s",
					 LZ4F_getErrorName(ctxError));

	return &streamer->base;
#else
	pg_fatal("this build does not support compression with %s", "LZ4");
	return NULL;				/* keep compiler quiet */
#endif
}

#ifdef USE_LZ4
/*
 * Compress the input data to output buffer.
 *
 * Find out the compression bound based on input data length for each
 * invocation to make sure that output buffer has enough capacity to
 * accommodate the compressed data. In case if the output buffer
 * capacity falls short of compression bound then forward the content
 * of output buffer to next streamer and empty the buffer.
 */
static void
astreamer_lz4_compressor_content(astreamer *streamer,
								 astreamer_member *member,
								 const char *data, int len,
								 astreamer_archive_context context)
{
	astreamer_lz4_frame *mystreamer;
	uint8	   *next_in,
			   *next_out;
	size_t		out_bound,
				compressed_size,
				avail_out;

	mystreamer = (astreamer_lz4_frame *) streamer;
	next_in = (uint8 *) data;

	/* Write header before processing the first input chunk. */
	if (!mystreamer->header_written)
	{
		compressed_size = LZ4F_compressBegin(mystreamer->cctx,
											 (uint8 *) mystreamer->base.bbs_buffer.data,
											 mystreamer->base.bbs_buffer.maxlen,
											 &mystreamer->prefs);

		if (LZ4F_isError(compressed_size))
			pg_log_error("could not write lz4 header: %s",
						 LZ4F_getErrorName(compressed_size));

		mystreamer->bytes_written += compressed_size;
		mystreamer->header_written = true;
	}

	/*
	 * Update the offset and capacity of output buffer based on number of
	 * bytes written to output buffer.
	 */
	next_out = (uint8 *) mystreamer->base.bbs_buffer.data + mystreamer->bytes_written;
	avail_out = mystreamer->base.bbs_buffer.maxlen - mystreamer->bytes_written;

	/*
	 * Find out the compression bound and make sure that output buffer has the
	 * required capacity for the success of LZ4F_compressUpdate. If needed
	 * forward the content to next streamer and empty the buffer.
	 */
	out_bound = LZ4F_compressBound(len, &mystreamer->prefs);
	if (avail_out < out_bound)
	{
		astreamer_content(mystreamer->base.bbs_next, member,
						  mystreamer->base.bbs_buffer.data,
						  mystreamer->bytes_written,
						  context);

		/* Enlarge buffer if it falls short of out bound. */
		if (mystreamer->base.bbs_buffer.maxlen < out_bound)
			enlargeStringInfo(&mystreamer->base.bbs_buffer, out_bound);

		avail_out = mystreamer->base.bbs_buffer.maxlen;
		mystreamer->bytes_written = 0;
		next_out = (uint8 *) mystreamer->base.bbs_buffer.data;
	}

	/*
	 * This call compresses the data starting at next_in and generates the
	 * output starting at next_out. It expects the caller to provide the size
	 * of input buffer and capacity of output buffer by providing parameters
	 * len and avail_out.
	 *
	 * It returns the number of bytes compressed to output buffer.
	 */
	compressed_size = LZ4F_compressUpdate(mystreamer->cctx,
										  next_out, avail_out,
										  next_in, len, NULL);

	if (LZ4F_isError(compressed_size))
		pg_log_error("could not compress data: %s",
					 LZ4F_getErrorName(compressed_size));

	mystreamer->bytes_written += compressed_size;
}

/*
 * End-of-stream processing.
 */
static void
astreamer_lz4_compressor_finalize(astreamer *streamer)
{
	astreamer_lz4_frame *mystreamer;
	uint8	   *next_out;
	size_t		footer_bound,
				compressed_size,
				avail_out;

	mystreamer = (astreamer_lz4_frame *) streamer;

	/* Find out the footer bound and update the output buffer. */
	footer_bound = LZ4F_compressBound(0, &mystreamer->prefs);
	if ((mystreamer->base.bbs_buffer.maxlen - mystreamer->bytes_written) <
		footer_bound)
	{
		astreamer_content(mystreamer->base.bbs_next, NULL,
						  mystreamer->base.bbs_buffer.data,
						  mystreamer->bytes_written,
						  ASTREAMER_UNKNOWN);

		/* Enlarge buffer if it falls short of footer bound. */
		if (mystreamer->base.bbs_buffer.maxlen < footer_bound)
			enlargeStringInfo(&mystreamer->base.bbs_buffer, footer_bound);

		avail_out = mystreamer->base.bbs_buffer.maxlen;
		mystreamer->bytes_written = 0;
		next_out = (uint8 *) mystreamer->base.bbs_buffer.data;
	}
	else
	{
		next_out = (uint8 *) mystreamer->base.bbs_buffer.data + mystreamer->bytes_written;
		avail_out = mystreamer->base.bbs_buffer.maxlen - mystreamer->bytes_written;
	}

	/*
	 * Finalize the frame and flush whatever data remaining in compression
	 * context.
	 */
	compressed_size = LZ4F_compressEnd(mystreamer->cctx,
									   next_out, avail_out, NULL);

	if (LZ4F_isError(compressed_size))
		pg_log_error("could not end lz4 compression: %s",
					 LZ4F_getErrorName(compressed_size));

	mystreamer->bytes_written += compressed_size;

	astreamer_content(mystreamer->base.bbs_next, NULL,
					  mystreamer->base.bbs_buffer.data,
					  mystreamer->bytes_written,
					  ASTREAMER_UNKNOWN);

	astreamer_finalize(mystreamer->base.bbs_next);
}

/*
 * Free memory.
 */
static void
astreamer_lz4_compressor_free(astreamer *streamer)
{
	astreamer_lz4_frame *mystreamer;

	mystreamer = (astreamer_lz4_frame *) streamer;
	astreamer_free(streamer->bbs_next);
	LZ4F_freeCompressionContext(mystreamer->cctx);
	pfree(streamer->bbs_buffer.data);
	pfree(streamer);
}
#endif

/*
 * Create a new base backup streamer that performs decompression of lz4
 * compressed blocks.
 */
astreamer *
astreamer_lz4_decompressor_new(astreamer *next)
{
#ifdef USE_LZ4
	astreamer_lz4_frame *streamer;
	LZ4F_errorCode_t ctxError;

	Assert(next != NULL);

	streamer = palloc0(sizeof(astreamer_lz4_frame));
	*((const astreamer_ops **) &streamer->base.bbs_ops) =
		&astreamer_lz4_decompressor_ops;

	streamer->base.bbs_next = next;
	initStringInfo(&streamer->base.bbs_buffer);

	/* Initialize internal stream state for decompression */
	ctxError = LZ4F_createDecompressionContext(&streamer->dctx, LZ4F_VERSION);
	if (LZ4F_isError(ctxError))
		pg_fatal("could not initialize compression library: %s",
				 LZ4F_getErrorName(ctxError));

	return &streamer->base;
#else
	pg_fatal("this build does not support compression with %s", "LZ4");
	return NULL;				/* keep compiler quiet */
#endif
}

#ifdef USE_LZ4
/*
 * Decompress the input data to output buffer until we run out of input
 * data. Each time the output buffer is full, pass on the decompressed data
 * to the next streamer.
 */
static void
astreamer_lz4_decompressor_content(astreamer *streamer,
								   astreamer_member *member,
								   const char *data, int len,
								   astreamer_archive_context context)
{
	astreamer_lz4_frame *mystreamer;
	uint8	   *next_in,
			   *next_out;
	size_t		avail_in,
				avail_out;

	mystreamer = (astreamer_lz4_frame *) streamer;
	next_in = (uint8 *) data;
	next_out = (uint8 *) mystreamer->base.bbs_buffer.data + mystreamer->bytes_written;
	avail_in = len;
	avail_out = mystreamer->base.bbs_buffer.maxlen - mystreamer->bytes_written;

	while (avail_in > 0)
	{
		size_t		ret,
					read_size,
					out_size;

		read_size = avail_in;
		out_size = avail_out;

		/*
		 * This call decompresses the data starting at next_in and generates
		 * the output data starting at next_out. It expects the caller to
		 * provide size of the input buffer and total capacity of the output
		 * buffer by providing the read_size and out_size parameters
		 * respectively.
		 *
		 * Per the documentation of LZ4, parameters read_size and out_size
		 * behaves as dual parameters. On return, the number of bytes consumed
		 * from the input buffer will be written back to read_size and the
		 * number of bytes decompressed to output buffer will be written back
		 * to out_size respectively.
		 */
		ret = LZ4F_decompress(mystreamer->dctx,
							  next_out, &out_size,
							  next_in, &read_size, NULL);

		if (LZ4F_isError(ret))
			pg_log_error("could not decompress data: %s",
						 LZ4F_getErrorName(ret));

		/* Update input buffer based on number of bytes consumed */
		avail_in -= read_size;
		next_in += read_size;

		mystreamer->bytes_written += out_size;

		/*
		 * If output buffer is full then forward the content to next streamer
		 * and update the output buffer.
		 */
		if (mystreamer->bytes_written >= mystreamer->base.bbs_buffer.maxlen)
		{
			astreamer_content(mystreamer->base.bbs_next, member,
							  mystreamer->base.bbs_buffer.data,
							  mystreamer->base.bbs_buffer.maxlen,
							  context);

			avail_out = mystreamer->base.bbs_buffer.maxlen;
			mystreamer->bytes_written = 0;
			next_out = (uint8 *) mystreamer->base.bbs_buffer.data;
		}
		else
		{
			avail_out = mystreamer->base.bbs_buffer.maxlen - mystreamer->bytes_written;
			next_out += mystreamer->bytes_written;
		}
	}
}

/*
 * End-of-stream processing.
 */
static void
astreamer_lz4_decompressor_finalize(astreamer *streamer)
{
	astreamer_lz4_frame *mystreamer;

	mystreamer = (astreamer_lz4_frame *) streamer;

	/*
	 * End of the stream, if there is some pending data in output buffers then
	 * we must forward it to next streamer.
	 */
	astreamer_content(mystreamer->base.bbs_next, NULL,
					  mystreamer->base.bbs_buffer.data,
					  mystreamer->base.bbs_buffer.maxlen,
					  ASTREAMER_UNKNOWN);

	astreamer_finalize(mystreamer->base.bbs_next);
}

/*
 * Free memory.
 */
static void
astreamer_lz4_decompressor_free(astreamer *streamer)
{
	astreamer_lz4_frame *mystreamer;

	mystreamer = (astreamer_lz4_frame *) streamer;
	astreamer_free(streamer->bbs_next);
	LZ4F_freeDecompressionContext(mystreamer->dctx);
	pfree(streamer->bbs_buffer.data);
	pfree(streamer);
}
#endif
